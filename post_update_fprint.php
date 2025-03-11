<?php
// Configurações do banco de dados
$host = "localhost";  // Ajuste conforme necessário
$user = "user";
$password = "senha";
$database = "banco";

// Conexão com o banco de dados
$conn = mysqli_connect($host, $user, $password, $database);

// Verifica a conexão
if (!$conn) {
    die(json_encode(["erro" => "Falha na conexão: " . mysqli_connect_error()]));
}

// Obtém os dados enviados via POST
$idcard = isset($_POST['idcard']) ? mysqli_real_escape_string($conn, $_POST['idcard']) : '';
$pessoa_id = isset($_POST['pessoa_id']) ? mysqli_real_escape_string($conn, $_POST['pessoa_id']) : '';
$codigo_dispositivo = isset($_POST['codigo_dispositivo']) ? mysqli_real_escape_string($conn, $_POST['codigo_dispositivo']) : '';

// Verifica se os parâmetros foram informados
if (empty($idcard) || empty($pessoa_id) || empty($codigo_dispositivo)) {
    echo json_encode(["erro" => "Parâmetros incompletos"]);
    exit;
}

// Verifica se o registro existe antes de atualizar
$sql_verifica = "SELECT * FROM Acesso_Pessoas_Dispositivos apd
                 JOIN Pessoas p ON apd.pessoa_id = p.pessoa_id
                 JOIN Dispositivos d ON apd.dispositivo_id = d.dispositivo_id
                 WHERE p.pessoa_idcard = '$idcard'
                 AND apd.pessoa_id = '$pessoa_id'
                 AND d.dispositivo_codigo = '$codigo_dispositivo'";

$result = mysqli_query($conn, $sql_verifica);

if (mysqli_num_rows($result) > 0) {
    // Atualiza o registro
    $sql_update = "UPDATE Acesso_Pessoas_Dispositivos apd
                   JOIN Pessoas p ON apd.pessoa_id = p.pessoa_id
                   JOIN Dispositivos d ON apd.dispositivo_id = d.dispositivo_id
                   SET apd.fingerprint_capturada = 1
                   WHERE p.pessoa_idcard = '$idcard'
                   AND apd.pessoa_id = '$pessoa_id'
                   AND d.dispositivo_codigo = '$codigo_dispositivo'";

    if (mysqli_query($conn, $sql_update)) {
        echo json_encode(["sucesso" => "Fingerprint capturada com sucesso"]);
    } else {
        echo json_encode(["erro" => "Erro ao atualizar: " . mysqli_error($conn)]);
    }
} else {
    echo json_encode(["erro" => "Registro não encontrado"]);
}

// Fecha a conexão
mysqli_close($conn);
?>

<?php
// Configuração do banco de dados
$host = "localhost";
$user = "user";
$password = "senha";
$database = "banco";

// Conexão com o banco
$conn = mysqli_connect($host, $user, $password, $database);

// Verifica conexão
if (!$conn) {
    die(json_encode(["erro" => "Falha na conexão: " . mysqli_connect_error()]));
}

// Obtém o log enviado via POST
$log = isset($_POST['log']) ? mysqli_real_escape_string($conn, $_POST['log']) : '';

// Verifica se o log foi recebido
if (empty($log)) {
    echo json_encode(["erro" => "Log não informado"]);
    exit;
}

// Insere o log na tabela Auditoria
$sql = "INSERT INTO Auditoria (auditoria_evento) VALUES ('$log')";

if (mysqli_query($conn, $sql)) {
    echo json_encode(["sucesso" => "Log armazenado com sucesso"]);
} else {
    echo json_encode(["erro" => "Erro ao armazenar log: " . mysqli_error($conn)]);
}

// Fecha a conexão
mysqli_close($conn);
?>
